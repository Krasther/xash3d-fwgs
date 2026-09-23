/*
in_commandmenu.c - lightweight engine-side GoldSrc commandmenu.txt support
Copyright (C) 2026

This is intentionally implemented in the engine so Android can use the
existing CS16Client package unchanged. It only adds the command menu overlay
and input handling; normal game/UI menus remain owned by the client/UI DLLs.
*/

#include "common.h"
#include "client.h"
#include "input.h"

#define COMMANDMENU_MAX_ITEMS 512
#define COMMANDMENU_MAX_NODES 128
#define COMMANDMENU_MAX_NODE_ITEMS 64

typedef struct
{
	int slot;
	char text[128];
	char command[256];
	char mapName[64];
	int teamOnly;
	int childNode;
	qboolean toggle;
} commandmenu_item_t;

typedef struct
{
	int parentNode;
	int itemIndices[COMMANDMENU_MAX_NODE_ITEMS];
	int itemCount;
} commandmenu_node_t;

static commandmenu_item_t cmdmenu_items[COMMANDMENU_MAX_ITEMS];
static commandmenu_node_t cmdmenu_nodes[COMMANDMENU_MAX_NODES];
static int cmdmenu_item_count;
static int cmdmenu_node_count;
static int cmdmenu_current_node;
static qboolean cmdmenu_active;
static qboolean cmdmenu_loaded;

static CVAR_DEFINE_AUTO( cmdmenu_scale, "1.8", FCVAR_ARCHIVE|FCVAR_FILTERABLE,
	"commandmenu.txt font scale multiplier" );

static int CommandMenu_CreateNode( int parent )
{
	int node;

	if( cmdmenu_node_count >= COMMANDMENU_MAX_NODES )
		return -1;

	node = cmdmenu_node_count++;
	memset( &cmdmenu_nodes[node], 0, sizeof( cmdmenu_nodes[node] ));
	cmdmenu_nodes[node].parentNode = parent;
	return node;
}

static int CommandMenu_KeyToSlot( const char *key )
{
	if( !key || !key[0] )
		return 0;

	if( key[0] >= '1' && key[0] <= '9' && !key[1] )
		return key[0] - '0';

	if( key[0] == '0' && !key[1] )
		return 10;

	return 0;
}

static int CommandMenu_AddItem( int node, const char *boundKey, const char *text,
	const char *command, const char *mapName, int teamOnly, qboolean toggle )
{
	commandmenu_item_t *item;
	int itemIndex;

	if( node < 0 || node >= cmdmenu_node_count )
		return -1;

	if( cmdmenu_item_count >= COMMANDMENU_MAX_ITEMS )
		return -1;

	if( cmdmenu_nodes[node].itemCount >= COMMANDMENU_MAX_NODE_ITEMS )
		return -1;

	itemIndex = cmdmenu_item_count++;
	item = &cmdmenu_items[itemIndex];
	memset( item, 0, sizeof( *item ));

	item->slot = CommandMenu_KeyToSlot( boundKey );
	item->teamOnly = teamOnly;
	item->childNode = -1;
	item->toggle = toggle;

	Q_strncpy( item->text, text ? text : "", sizeof( item->text ));
	Q_strncpy( item->command, command ? command : "", sizeof( item->command ));
	Q_strncpy( item->mapName, mapName ? mapName : "", sizeof( item->mapName ));

	cmdmenu_nodes[node].itemIndices[cmdmenu_nodes[node].itemCount++] = itemIndex;
	return itemIndex;
}

static void CommandMenu_Reset( void )
{
	memset( cmdmenu_items, 0, sizeof( cmdmenu_items ));
	memset( cmdmenu_nodes, 0, sizeof( cmdmenu_nodes ));
	cmdmenu_item_count = 0;
	cmdmenu_node_count = 0;
	cmdmenu_current_node = 0;
	cmdmenu_loaded = false;
}

static char *CommandMenu_ParseToken( char *cursor, char *token, size_t tokenSize )
{
	if( !cursor )
		return NULL;

	return COM_ParseFileSafe( cursor, token, tokenSize, 0, NULL, NULL );
}

static qboolean CommandMenu_ParseFile( void )
{
	fs_offset_t fileLength = 0;
	byte *source;
	char *cursor;
	char token[MAX_TOKEN];
	int currentNode = 0;

	CommandMenu_Reset();
	if( CommandMenu_CreateNode( -1 ) < 0 )
		return false;

	source = FS_LoadFile( "commandmenu.txt", &fileLength, true );
	if( !source )
	{
		Con_Printf( "Unable to open commandmenu.txt\n" );
		return false;
	}

	cursor = (char *)source;
	if( fileLength >= 3 && source[0] == 0xef && source[1] == 0xbb && source[2] == 0xbf )
		cursor += 3;

	while(( cursor = CommandMenu_ParseToken( cursor, token, sizeof( token ))) != NULL && token[0] )
	{
		char mapName[64] = "";
		int teamOnly = -1;
		qboolean toggle = false;
		qboolean custom = false;
		char boundKey[32];
		char itemText[256];
		char command[512];
		int itemIndex;

		if( !Q_strcmp( token, "}" ))
		{
			if( currentNode > 0 )
				currentNode = cmdmenu_nodes[currentNode].parentNode;
			continue;
		}

		if( !Q_strcmp( token, "{" ))
			continue;

		if( !Q_stricmp( token, "CUSTOM" ))
		{
			custom = true;
			cursor = CommandMenu_ParseToken( cursor, token, sizeof( token ));
			if( !cursor ) break;
		}
		else if( !Q_stricmp( token, "MAP" ))
		{
			cursor = CommandMenu_ParseToken( cursor, token, sizeof( token ));
			if( !cursor ) break;
			Q_strncpy( mapName, token, sizeof( mapName ));

			cursor = CommandMenu_ParseToken( cursor, token, sizeof( token ));
			if( !cursor ) break;
		}
		else if( !Q_strnicmp( token, "TEAM", 4 ))
		{
			teamOnly = Q_atoi( token + 4 );
			cursor = CommandMenu_ParseToken( cursor, token, sizeof( token ));
			if( !cursor ) break;
		}
		else if( !Q_strnicmp( token, "TOGGLE", 6 ))
		{
			toggle = true;
			cursor = CommandMenu_ParseToken( cursor, token, sizeof( token ));
			if( !cursor ) break;
		}

		Q_strncpy( boundKey, token, sizeof( boundKey ));

		cursor = CommandMenu_ParseToken( cursor, itemText, sizeof( itemText ));
		if( !cursor ) break;

		cursor = CommandMenu_ParseToken( cursor, command, sizeof( command ));
		if( !cursor ) break;

		if( custom && !Q_stricmp( command, "!CHANGETEAM" ))
			Q_strncpy( command, "chooseteam", sizeof( command ));
		else if( custom && command[0] == '!' )
			command[0] = '\0';

		itemIndex = CommandMenu_AddItem( currentNode, boundKey, itemText,
			!Q_strcmp( command, "{" ) ? "" : command,
			mapName, teamOnly, toggle );

		if( itemIndex >= 0 && !Q_strcmp( command, "{" ))
		{
			int child = CommandMenu_CreateNode( currentNode );
			if( child >= 0 )
			{
				cmdmenu_items[itemIndex].childNode = child;
				currentNode = child;
			}
		}
	}

	Mem_Free( source );

	cmdmenu_loaded = cmdmenu_nodes[0].itemCount > 0;
	if( !cmdmenu_loaded )
		Con_Printf( "commandmenu.txt contained no usable menu entries\n" );

	return cmdmenu_loaded;
}

static qboolean CommandMenu_MapMatches( const char *wantedMap )
{
	if( COM_StringEmptyOrNULL( wantedMap ))
		return true;

	if( COM_StringEmptyOrNULL( clgame.mapname ))
		return false;

	return !Q_stricmp( wantedMap, clgame.mapname );
}

static qboolean CommandMenu_ItemVisible( const commandmenu_item_t *item )
{
	/*
	 * GoldSrc TEAM filters depend on game-specific team state. The engine
	 * deliberately does not guess Counter-Strike's private team number here.
	 * MAP filters are safe and generic, so honor those.
	 */
	(void)item->teamOnly;
	return CommandMenu_MapMatches( item->mapName );
}

static int CommandMenu_FindItem( int node, int slot )
{
	int i;

	if( node < 0 || node >= cmdmenu_node_count )
		return -1;

	for( i = 0; i < cmdmenu_nodes[node].itemCount; i++ )
	{
		int itemIndex = cmdmenu_nodes[node].itemIndices[i];
		commandmenu_item_t *item = &cmdmenu_items[itemIndex];

		if( item->slot == slot && CommandMenu_ItemVisible( item ))
			return itemIndex;
	}

	return -1;
}

static void CommandMenu_Close( void )
{
	cmdmenu_active = false;
	cmdmenu_current_node = 0;
}

static void CommandMenu_Execute( const commandmenu_item_t *item )
{
	if( COM_StringEmptyOrNULL( item->command ))
		return;

	if( item->toggle )
	{
		convar_t *cvar = Cvar_FindVar( item->command );
		if( cvar )
		{
			Cvar_DirectSetValue( cvar, cvar->value == 0.0f ? 1.0f : 0.0f );
			return;
		}
	}

	Cbuf_AddText( item->command );
	Cbuf_AddText( "\n" );
}

static void CommandMenu_SelectSlot( int slot )
{
	int itemIndex;
	commandmenu_item_t *item;

	if( slot == 10 )
	{
		if( cmdmenu_current_node > 0 )
			cmdmenu_current_node = cmdmenu_nodes[cmdmenu_current_node].parentNode;
		else
			CommandMenu_Close();
		return;
	}

	itemIndex = CommandMenu_FindItem( cmdmenu_current_node, slot );
	if( itemIndex < 0 )
		return;

	item = &cmdmenu_items[itemIndex];
	if( item->childNode >= 0 )
	{
		cmdmenu_current_node = item->childNode;
		return;
	}

	CommandMenu_Execute( item );
	CommandMenu_Close();
}

static void CommandMenu_Open_f( void )
{
	if( cls.state != ca_active )
		return;

	if( cmdmenu_active )
	{
		CommandMenu_Close();
		return;
	}

	if( !CommandMenu_ParseFile())
		return;

	cmdmenu_current_node = 0;
	cmdmenu_active = true;
}

static void CommandMenu_Release_f( void )
{
	/* Keep menu open after releasing a +commandmenu bind. */
}

static void CommandMenu_Reload_f( void )
{
	qboolean reopen = cmdmenu_active;
	CommandMenu_Close();

	if( CommandMenu_ParseFile() && reopen )
		cmdmenu_active = true;
}

void CommandMenu_Init( void )
{
	Cvar_RegisterVariable( &cmdmenu_scale );
	Cmd_AddCommand( "+commandmenu", CommandMenu_Open_f, "open commandmenu.txt" );
	Cmd_AddCommand( "-commandmenu", CommandMenu_Release_f, "release commandmenu key" );
	Cmd_AddCommand( "commandmenu", CommandMenu_Open_f, "toggle commandmenu.txt" );
	Cmd_AddCommand( "commandmenu_reload", CommandMenu_Reload_f, "reload commandmenu.txt" );
}

qboolean CommandMenu_KeyEvent( int key, int down )
{
	int slot = 0;

	if( !cmdmenu_active )
		return false;

	if( key == K_ESCAPE )
	{
		if( down )
		{
			if( cmdmenu_current_node > 0 )
				cmdmenu_current_node = cmdmenu_nodes[cmdmenu_current_node].parentNode;
			else
				CommandMenu_Close();
		}
		return true;
	}

	if( key >= '1' && key <= '9' )
		slot = key - '0';
	else if( key == '0' )
		slot = 10;

	if( slot )
	{
		if( down )
			CommandMenu_SelectSlot( slot );
		return true;
	}

	return false;
}

static void CommandMenu_MakeScaledFont( cl_font_t *out )
{
	float scale;
	int i;

	*out = cls.creditsFont;
	if( !out->valid )
		*out = *Con_GetCurFont();

	scale = bound( 0.75f, cmdmenu_scale.value, 4.0f );
	out->scale *= scale;
	out->charHeight = Q_max( 1, Q_rint( out->charHeight * scale ));

	for( i = 0; i < ARRAYSIZE( out->charWidths ); i++ )
	{
		if( out->charWidths[i] )
			out->charWidths[i] = bound( 1, Q_rint( out->charWidths[i] * scale ), 255 );
	}
}

void CommandMenu_Draw( void )
{
	cl_font_t font;
	rgba_t white = { 255, 255, 255, 255 };
	rgba_t shadow = { 0, 0, 0, 210 };
	rgba_t accent = { 120, 255, 120, 255 };
	commandmenu_node_t *node;
	float x, y, lineHeight;
	int i;

	if( !cmdmenu_active || cls.state != ca_active || cmdmenu_current_node < 0 || cmdmenu_current_node >= cmdmenu_node_count )
		return;

	CommandMenu_MakeScaledFont( &font );
	if( !font.valid )
		return;

	x = Q_max( 12.0f, refState.width * 0.008f );
	y = refState.height * 0.34f;
	lineHeight = font.charHeight * 1.08f;

	CL_DrawString( x + 1, y + 1, "Command Menu", shadow, &font, FONT_DRAW_UTF8|FONT_DRAW_FORCECOL );
	CL_DrawString( x, y, "Command Menu", white, &font, FONT_DRAW_UTF8|FONT_DRAW_FORCECOL );
	y += lineHeight * 1.35f;

	node = &cmdmenu_nodes[cmdmenu_current_node];
	for( i = 0; i < node->itemCount; i++ )
	{
		commandmenu_item_t *item = &cmdmenu_items[node->itemIndices[i]];
		char line[256];

		if( !CommandMenu_ItemVisible( item ) || item->slot < 1 || item->slot > 9 )
			continue;

		Q_snprintf( line, sizeof( line ), "%d. %s%s",
			item->slot, item->text, item->childNode >= 0 ? "  >" : "" );

		CL_DrawString( x + 1, y + 1, line, shadow, &font, FONT_DRAW_UTF8|FONT_DRAW_FORCECOL );
		CL_DrawString( x, y, line, item->childNode >= 0 ? accent : white,
			&font, FONT_DRAW_UTF8|FONT_DRAW_FORCECOL );
		y += lineHeight;
	}

	{
		const char *back = cmdmenu_current_node > 0 ? "0. Back" : "0. Close";
		CL_DrawString( x + 1, y + 1, back, shadow, &font, FONT_DRAW_UTF8|FONT_DRAW_FORCECOL );
		CL_DrawString( x, y, back, white, &font, FONT_DRAW_UTF8|FONT_DRAW_FORCECOL );
	}
}
